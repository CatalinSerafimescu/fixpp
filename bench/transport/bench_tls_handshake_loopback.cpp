// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// bench/transport/bench_tls_handshake_loopback.cpp
// T022 (014 FR-013b) — TLS 1.3 1-RTT loopback handshake latency baseline.
//
// Measures wall-clock time for a full TLS 1.3 1-RTT handshake over loopback
// (127.0.0.1). Both sides run in the same process; the acceptor runs a
// background thread for each handshake.
//
// Each iteration:
//   1. Acceptor thread accepts one connection + runs server-side handshake.
//   2. Benchmark thread connects + runs client-side handshake.
//   3. Measure latency of the paired connect+handshake.
//
// Wired to the real asio_tls_transport_factory + loopback acceptor using
// leaf_rsa2048.pem + ca.pem (mtls_ca profile; clock=null → no expiry check).
//
// CMake target: bench_tls_handshake_loopback (bench/transport/CMakeLists.txt:22)
//
// Design anchor: 014 tasks.md T022; FR-013b; plan §Performance; data-model E-5.
// Baseline: first real 1-RTT number (no regression gate — recording only).
//
// Build:
//   cmake --build build/linux-clang-debug --target bench_tls_handshake_loopback -j2
//   FIXPP_TLS_FIXTURE_DIR=tests/tls/fixtures ./build/linux-clang-debug/bench/transport/bench_tls_handshake_loopback

#include <benchmark/benchmark.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>

// Listener lives in src/ (not a public header).
#include "transport/asio_listener.hpp"

namespace {

using fixpp::transport::Endpoint;
using fixpp::transport::TlsTransport;
using fixpp::transport::asio_listener;

// ─────────────────────────────────────────────────────────────────────────────
// Build an SslCtxConfig from the fixture directory (mtls_ca, no expiry).
// ─────────────────────────────────────────────────────────────────────────────
fixpp::tls::SslCtxConfig make_ssl_cfg(const std::string& fixture_dir) {
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path        = fixture_dir + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = fixture_dir + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path   = fixture_dir + "/ca.pem";

    auto cs = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    if (!cs.has_value()) {
        throw std::runtime_error("bench: failed to build file_cert_source");
    }

    fixpp::tls::SslCtxConfig cfg;
    cfg.profile = fixpp::tls::SecurityProfile::mtls_ca;
    cfg.cs      = std::move(*cs);
    cfg.clock   = nullptr;  // skip expiry — fixture certs may be stale
    cfg.caps    = fixpp::tls::CertSourceCaps{};
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// BM_TlsHandshakeLoopback
//
// Measures wall-clock time for a TLS 1.3 1-RTT handshake over 127.0.0.1:port.
// Both sides run in the same process.
//
// Each iteration:
//   1. Server side: async_accept + async_handshake (in a background thread).
//   2. Client side: fresh transport mint → async_connect → async_handshake.
//   3. Measure from before connect to after handshake.
//
// This matches the reconnect path: factory mints a fresh Transport per attempt.
// ─────────────────────────────────────────────────────────────────────────────
class TlsHandshakeFixture : public benchmark::Fixture {
public:
    void SetUp(const benchmark::State& /*state*/) override {
        const char* dir_env = std::getenv("FIXPP_TLS_FIXTURE_DIR");
#ifdef FIXPP_TLS_FIXTURE_DIR
        static const char* kCompileTimeDir = FIXPP_TLS_FIXTURE_DIR;
#else
        static const char* kCompileTimeDir = nullptr;
#endif
        fixture_dir_ = dir_env ? dir_env : (kCompileTimeDir ? kCompileTimeDir : "");
        if (fixture_dir_.empty()) {
            // Can't run bench without fixtures; return and let iterations be no-ops.
            return;
        }

        ssl_cfg_ = make_ssl_cfg(fixture_dir_);

        // Build the client factory (SSL_CTX cached once per FR-026).
        auto factory_result = fixpp::transport::make_asio_tls_transport_factory(
            fixpp::transport::Transport::Config{}, ssl_cfg_);
        if (!factory_result.has_value()) {
            throw std::runtime_error("bench: make_asio_tls_transport_factory failed");
        }
        client_factory_ = std::move(*factory_result);

        // Build the server listener (OS-assigned port).
        asio_listener::Config listener_cfg;
        listener_cfg.bind_endpoint = Endpoint{"127.0.0.1", 0, /*backlog=*/32};
        listener_cfg.ssl_cfg       = ssl_cfg_;
        listener_ = std::make_unique<asio_listener>(server_ioc_.get_executor(), listener_cfg);
        server_port_ = listener_->bound_endpoint().port;

        // Start the server io_context thread.
        server_thread_ = std::thread([this] { server_ioc_.run(); });

        ready_ = true;
    }

    void TearDown(const benchmark::State& /*state*/) override {
        if (!ready_) { return; }
        // Cancel the listener and stop the server ioc.
        (void)listener_->cancel();
        server_ioc_.stop();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        ready_ = false;
    }

    // Run one full TLS handshake (accept + connect + both-side handshakes).
    // Returns latency in microseconds. Returns -1 if fixtures unavailable.
    double run_one_handshake() {
        if (!ready_) { return -1.0; }

        asio::io_context client_ioc;
        bool server_hs_ok = false;
        bool client_hs_ok = false;
        std::exception_ptr server_exc;

        // Server coroutine: accept + handshake (posted on server_ioc_).
        asio::co_spawn(
            server_ioc_.get_executor(),
            [&, this]() -> asio::awaitable<void> {
                try {
                    auto ar = co_await listener_->async_accept();
                    if (!ar.has_value()) { co_return; }
                    auto* tls = dynamic_cast<TlsTransport*>(ar->get());
                    if (!tls) { co_return; }
                    auto hs = co_await tls->async_handshake(ssl_cfg_);
                    server_hs_ok = hs.has_value();
                } catch (...) {
                    server_exc = std::current_exception();
                }
            },
            asio::detached);

        // Client coroutine: connect + handshake.
        auto t0 = std::chrono::steady_clock::now();
        auto client_result = asio::co_spawn(
            client_ioc.get_executor(),
            [&, this]() -> asio::awaitable<bool> {
                auto t = client_factory_->make(client_ioc.get_executor(), ssl_cfg_, nullptr);
                if (!t.has_value()) { co_return false; }
                auto* transport = t->get();

                Endpoint ep{"127.0.0.1", server_port_, 0};
                auto conn = co_await (*t)->async_connect(ep);
                if (!conn.has_value()) { co_return false; }

                auto* tls = dynamic_cast<TlsTransport*>(transport);
                if (!tls) { co_return false; }
                auto hs = co_await tls->async_handshake(ssl_cfg_);
                co_return hs.has_value();
            },
            asio::use_future);

        client_ioc.run();
        auto t1 = std::chrono::steady_clock::now();

        client_hs_ok = client_result.get();

        auto us = static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

        return (client_hs_ok) ? us : -1.0;
    }

private:
    std::string                                           fixture_dir_;
    fixpp::tls::SslCtxConfig                              ssl_cfg_;
    std::shared_ptr<fixpp::transport::TransportFactory>   client_factory_;
    asio::io_context                                      server_ioc_;
    std::unique_ptr<asio_listener>                        listener_;
    std::uint16_t                                         server_port_{0};
    std::thread                                           server_thread_;
    bool                                                  ready_{false};
};

BENCHMARK_DEFINE_F(TlsHandshakeFixture, Handshake1Rtt)(benchmark::State& state) {
    std::size_t count = 0;
    double total_us = 0.0;

    for (auto _ : state) {
        const double us = run_one_handshake();
        if (us >= 0.0) {
            total_us += us;
            ++count;
        }
    }

    if (count > 0) {
        state.counters["avg_us"] = total_us / static_cast<double>(count);
    }
}

// Run with a moderate iteration count (TLS handshake is I/O-bound and slow).
BENCHMARK_REGISTER_F(TlsHandshakeFixture, Handshake1Rtt)
    ->Iterations(20)
    ->Unit(benchmark::kMillisecond);

}  // namespace

BENCHMARK_MAIN();
